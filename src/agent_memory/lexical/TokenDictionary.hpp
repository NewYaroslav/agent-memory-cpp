#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_TOKEN_DICTIONARY_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_TOKEN_DICTIONARY_HPP_INCLUDED

/// \file TokenDictionary.hpp
/// \brief Token dictionary value types.

#include "Lexical.hpp"

#include <cstddef>
#include <string>

namespace agent_memory {

    /// \brief Dictionary entry for one normalized token.
    struct TokenDictionaryEntry final {
        TokenId id;
        std::string text;
        std::size_t document_frequency = 0;

        /// \brief Returns true when id or text is empty.
        [[nodiscard]] bool empty() const noexcept;
    };

} // namespace agent_memory

#endif
