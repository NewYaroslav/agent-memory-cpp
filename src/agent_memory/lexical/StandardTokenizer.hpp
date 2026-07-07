#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_STANDARD_TOKENIZER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_STANDARD_TOKENIZER_HPP_INCLUDED

/// \file StandardTokenizer.hpp
/// \brief Std-only tokenizer implementation.

#include "ITokenizer.hpp"

namespace agent_memory {

    /// \brief Dependency-free tokenizer for UTF-8 text, markdown, and code-like text.
    class StandardTokenizer final : public ITokenizer {
    public:
        [[nodiscard]] TokenizationResult tokenize(
            std::string_view text,
            const TokenizeOptions& options = {}
        ) const override;
    };

} // namespace agent_memory

#endif
