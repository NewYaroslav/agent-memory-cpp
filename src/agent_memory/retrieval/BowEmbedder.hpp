#pragma once
#ifndef AGENT_MEMORY_HEADER_RETRIEVAL_BOW_EMBEDDER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_RETRIEVAL_BOW_EMBEDDER_HPP_INCLUDED

/// \file BowEmbedder.hpp
/// \brief Bag-of-Words dense embedder used as a deterministic control point.
///
/// The embedder is intentionally trivial: it tokenizes text by lowercasing
/// ASCII letters and splitting on any non-alphanumeric boundary, drops tokens
/// shorter than two characters, and emits a dense float vector of size
/// `dictionary_size()` where each position stores the raw term count.
/// Vectors are L2-normalized so cosine similarity reduces to a dot product
/// during top-K search.
///
/// The embedder is NOT a real semantic embedder. It is the canonical
/// control-point baseline that PR #31 (binary-bucket candidate filter) will
/// benchmark against. The interface is shaped so that future PRs can swap
/// in a real embedder (e.g. an external model backend) without changing any
/// consumer code.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace agent_memory {

    /// \brief Public baseline-name constant for the BoW exact-vector retriever.
    inline constexpr std::string_view kBaselineNameBowVector = "bow_vector";

    /// \brief Trivial deterministic Bag-of-Words embedder.
    ///
    /// The embedder owns its term dictionary, which is built incrementally by
    /// `add_corpus_text` and `embed`. The dictionary is stable once `build`
    /// has been called: subsequent `embed` calls only add terms already
    /// present in the dictionary.
    class BowEmbedder final {
    public:
        BowEmbedder();

        /// \brief Tokenizes `text` and adds every observed term to the dictionary.
        /// \note This does NOT compute an embedding; it only mutates the
        ///       dictionary. Use `embed` to obtain a dense vector.
        void add_corpus_text(std::string_view text);

        /// \brief Marks the dictionary as final. After `build`, `embed` no
        ///        longer extends the dictionary for unseen terms.
        void build();

        /// \brief Encodes `text` as a dense float vector of size
        ///        `dictionary_size()`. Each component is the raw term count,
        ///        then L2-normalized so cosine similarity equals dot product.
        /// \note If the embedder has not been `build`-ed yet, the dictionary
        ///       is grown for unseen terms to keep `embed` valid standalone.
        [[nodiscard]] std::vector<float> embed(std::string_view text) const;

        /// \brief Returns the number of distinct terms in the dictionary.
        [[nodiscard]] std::size_t dictionary_size() const noexcept;

        /// \brief Returns true after `build` has been called.
        [[nodiscard]] bool is_built() const noexcept;

    private:
        // Term -> dense index in the output vector.
        std::unordered_map<std::string, std::uint32_t> m_dictionary;
        bool m_built = false;
    };

} // namespace agent_memory

#endif