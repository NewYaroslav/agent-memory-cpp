#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_I_TOKEN_DICTIONARY_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_I_TOKEN_DICTIONARY_HPP_INCLUDED

/// \file ITokenDictionary.hpp
/// \brief Dependency-free token dictionary contract.

#include "TokenDictionary.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace agent_memory {

    /// \brief Contract for normalized token text to numeric token id mapping.
    ///
    /// Thread-safety:
    ///   Implementations are not required to be thread-safe. The intended
    ///   pattern is single-writer (during ingestion) and many-readers (during
    ///   query). Concurrent calls have unspecified behavior.
    ///
    /// Exception contract:
    ///   - Mutating methods (get_or_create, assign/increment_document_frequency,
    ///     erase, clear) may throw std::bad_alloc on allocation failure or
    ///     std::invalid_argument when a precondition is violated (e.g. unknown id).
    ///   - Query methods (size, find_by_*) are noexcept where the implementation
    ///     permits it.
    ///
    /// ID allocation:
    ///   TokenId values are assigned by get_or_create and never reused within
    ///   the lifetime of the dictionary. After clear(), implementations may
    ///   either reset the id counter to 1 or continue from the previous max+1;
    ///   callers must not assume monotonic continuity.
    class ITokenDictionary {
    public:
        virtual ~ITokenDictionary();

        /// \brief Number of dictionary entries.
        [[nodiscard]] virtual std::size_t size() const noexcept = 0;

        /// \brief Returns an existing id or creates a new id for normalized token text.
        /// \pre `text` must not be empty.
        [[nodiscard]] virtual TokenId get_or_create(std::string_view text) = 0;

        /// \brief Finds a dictionary entry by normalized token text.
        [[nodiscard]] virtual std::optional<TokenDictionaryEntry> find_by_text(
            std::string_view text
        ) const = 0;

        /// \brief Finds a dictionary entry by token id.
        [[nodiscard]] virtual std::optional<TokenDictionaryEntry> find_by_id(
            const TokenId& id
        ) const = 0;

        /// \brief Assigns document frequency for a token id.
        /// \pre `id` must exist in the dictionary.
        virtual void assign_document_frequency(const TokenId& id, std::size_t document_frequency) = 0;

        /// \brief Adds `delta` to the document frequency for an existing token id.
        /// \pre `id` must exist in the dictionary.
        /// \note Prefer this over assign_document_frequency during ingestion to
        ///       avoid the lost-update hazard when multiple documents race.
        virtual void increment_document_frequency(const TokenId& id, std::size_t delta) = 0;

        /// \brief Removes a dictionary entry.
        /// \return True when an entry was removed.
        [[nodiscard]] virtual bool erase(const TokenId& id) = 0;

        /// \brief Removes all dictionary entries.
        virtual void clear() = 0;
    };

} // namespace agent_memory

#endif