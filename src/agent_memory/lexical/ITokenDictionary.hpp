#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_I_TOKEN_DICTIONARY_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_I_TOKEN_DICTIONARY_HPP_INCLUDED

/// \file ITokenDictionary.hpp
/// \brief Dependency-free token dictionary contract.

#include "TokenDictionary.hpp"

#include <optional>

namespace agent_memory {

    /// \brief Contract for normalized token text to numeric token id mapping.
    class ITokenDictionary {
    public:
        virtual ~ITokenDictionary();

        /// \brief Number of dictionary entries.
        [[nodiscard]] virtual std::size_t size() const noexcept = 0;

        /// \brief Returns an existing id or creates a new id for normalized token text.
        /// \pre `text` must not be empty.
        virtual TokenId get_or_create(std::string text) = 0;

        /// \brief Finds a dictionary entry by normalized token text.
        [[nodiscard]] virtual std::optional<TokenDictionaryEntry> find_by_text(
            const std::string& text
        ) const = 0;

        /// \brief Finds a dictionary entry by token id.
        [[nodiscard]] virtual std::optional<TokenDictionaryEntry> find_by_id(
            TokenId id
        ) const = 0;

        /// \brief Updates document frequency for a token id.
        /// \pre `id` must exist in the dictionary.
        virtual void set_document_frequency(TokenId id, std::size_t document_frequency) = 0;

        /// \brief Removes a dictionary entry.
        /// \return True when an entry was removed.
        [[nodiscard]] virtual bool erase(TokenId id) = 0;

        /// \brief Removes all dictionary entries.
        virtual void clear() = 0;
    };

} // namespace agent_memory

#endif
