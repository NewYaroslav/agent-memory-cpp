#pragma once
#ifndef AGENT_MEMORY_HEADER_LEXICAL_LEXICAL_HASH_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_LEXICAL_LEXICAL_HASH_HPP_INCLUDED

/// \file LexicalHash.hpp
/// \brief std::hash specializations for lexical value types.

#include "Lexical.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace std {

/// \brief Hash specialization for agent_memory::TokenId.
/// \note Enables TokenId as a key in unordered_map / unordered_set (e.g.
///       postings dictionaries and inverted indexes).
template<>
struct hash<agent_memory::TokenId> {
    [[nodiscard]] std::size_t operator()(const agent_memory::TokenId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value());
    }
};

} // namespace std

#endif