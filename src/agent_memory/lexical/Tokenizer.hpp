#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_TOKENIZER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_TOKENIZER_HPP_INCLUDED

/// \file Tokenizer.hpp
/// \brief Dependency-free tokenizer value types.

#include <agent_memory/domain/Document.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace agent_memory {

    /// \brief Coarse token category produced by tokenizer backends.
    enum class TokenKind {
        Word,
        Number,
        Identifier,
        Path,
        Symbol,
        Custom
    };

    /// \brief Token emitted from UTF-8 source text.
    struct Token final {
        /// \brief Normalized lookup text.
        std::string text;
        /// \brief Byte range in the original UTF-8 source text.
        TextRange source_range;
        /// \brief Zero-based token position in the emitted stream.
        std::size_t position = 0;
        TokenKind kind = TokenKind::Word;

        /// \brief Returns true when the normalized token text is empty.
        [[nodiscard]] bool empty() const noexcept;
    };

    /// \brief Options shared by tokenizer implementations.
    struct TokenizeOptions final {
        /// \brief Lowercase ASCII letters during normalization.
        bool lowercase_ascii = true;
        /// \brief Emit punctuation and operators as symbol tokens.
        bool emit_symbol_tokens = false;
        /// \brief Emit searchable parts for code-style identifiers.
        bool emit_identifier_parts = true;
    };

    /// \brief Ordered tokenizer output.
    struct TokenizationResult final {
        std::vector<Token> tokens;

        /// \brief Returns true when no tokens were emitted.
        [[nodiscard]] bool empty() const noexcept;

        /// \brief Returns the number of emitted tokens.
        [[nodiscard]] std::size_t size() const noexcept;
    };

    /// \brief Returns stable lowercase token-kind name.
    [[nodiscard]] std::string_view to_string(TokenKind kind) noexcept;

    /// \brief Parses a stable token-kind name.
    [[nodiscard]] bool parse_token_kind(std::string_view text, TokenKind& out_kind) noexcept;

} // namespace agent_memory

#endif
