#pragma once
#ifndef AGENT_MEMORY_HEADER_RETRIEVAL_BOW_TOKENIZER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_RETRIEVAL_BOW_TOKENIZER_HPP_INCLUDED

/// \file BoWTokenizer.hpp
/// \brief Canonical Bag-of-Words tokenizer for `BowEmbedder`.
///
/// This is the canonical BoW tokenizer used by `BowEmbedder` for its
/// default-constructed form. It enforces the BoW tokenization contract
/// that PR #28's `BowVectorRetriever` test relies on:
///
///   - Lowercase ASCII letters (other bytes are kept as-is).
///   - Split on any non-alphanumeric byte boundary.
///   - Drop empty tokens.
///   - Drop tokens shorter than 2 characters.
///
/// The output of this tokenizer for `alpha-beta-gamma` is exactly
/// `{"alpha", "beta", "gamma"}` (three tokens). Note this differs from
/// `agent_memory::StandardTokenizer`, which treats `-`, `.`, `/`, `\`,
/// `:`, `_` as token-body characters and would emit `alpha-beta-gamma`
/// as a single token. The two tokenizers are intentionally separate
/// contracts — `StandardTokenizer` is for the rich lexical pipeline,
/// `BoWTokenizer` is for the dense-vector BoW control point.

#include <agent_memory/lexical/ITokenizer.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace agent_memory {

    /// \brief Thin `ITokenizer` that emits tokens suitable for BoW indexing.
    ///
    /// This is the canonical BoW tokenizer for `BowEmbedder`. It is a
    /// pure adapter over the BoW contract; `TokenizeOptions` are accepted
    /// to satisfy the `ITokenizer` interface but are ignored — the BoW
    /// contract is fixed.
    class BoWTokenizer final : public ITokenizer {
    public:
        [[nodiscard]] TokenizationResult tokenize(
            std::string_view text,
            const TokenizeOptions& /*options*/ = {}
        ) const override {
            TokenizationResult result;
            std::string current;
            current.reserve(8);
            std::size_t token_begin = 0;

            auto flush = [&](const std::size_t end_offset) {
                if(current.size() >= 2) {
                    Token token;
                    token.text = current;
                    token.source_range = TextRange{token_begin, end_offset - token_begin};
                    token.position = result.tokens.size();
                    token.kind = TokenKind::Word;
                    result.tokens.push_back(std::move(token));
                }
                current.clear();
            };

            for(std::size_t index = 0; index < text.size(); ++index) {
                const unsigned char raw = static_cast<unsigned char>(text[index]);
                const char ch = ascii_lower_byte(raw);
                const bool alnum = (ch >= 'a' && ch <= 'z')
                    || (ch >= '0' && ch <= '9');
                if(alnum) {
                    if(current.empty()) {
                        token_begin = index;
                    }
                    current.push_back(ch);
                } else if(!current.empty()) {
                    flush(index);
                }
            }
            if(!current.empty()) {
                flush(text.size());
            }
            return result;
        }

    private:
        [[nodiscard]] static char ascii_lower_byte(unsigned char value) noexcept {
            if(value >= 'A' && value <= 'Z') {
                return static_cast<char>(value - 'A' + 'a');
            }
            return static_cast<char>(value);
        }
    };

} // namespace agent_memory

#endif