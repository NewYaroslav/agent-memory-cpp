#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_I_TOKENIZER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_I_TOKENIZER_HPP_INCLUDED

/// \file ITokenizer.hpp
/// \brief Dependency-free tokenizer contract.

#include "Tokenizer.hpp"

#include <string_view>

namespace agent_memory {

    /// \brief Contract implemented by tokenizer backends.
    class ITokenizer {
    public:
        virtual ~ITokenizer();

        /// \brief Tokenizes UTF-8 source text.
        /// \param text Source text. The tokenizer does not retain this view.
        /// \param options Tokenization options.
        /// \return Ordered normalized tokens with byte ranges into `text`.
        [[nodiscard]] virtual TokenizationResult tokenize(
            std::string_view text,
            const TokenizeOptions& options = {}
        ) const = 0;
    };

} // namespace agent_memory

#endif
