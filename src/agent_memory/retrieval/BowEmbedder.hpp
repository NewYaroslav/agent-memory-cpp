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
///
/// Tokenization is delegated to the injected `ITokenizer&` and term-id
/// assignment to the injected `ITokenDictionary&`. The default-constructed
/// form wires `BoWTokenizer` and an internal in-memory dictionary, so
/// existing callers (notably `BowVectorRetriever`) keep working with no
/// change.

#include <agent_memory/lexical/ITokenDictionary.hpp>
#include <agent_memory/lexical/ITokenizer.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace agent_memory {

    class BoWTokenizer;

    /// \brief Public baseline-name constant for the BoW exact-vector retriever.
    inline constexpr std::string_view kBaselineNameBowVector = "bow_vector";

    /// \brief Trivial deterministic Bag-of-Words embedder.
    ///
    /// The embedder delegates tokenization to an injected `ITokenizer&` and
    /// term-id assignment to an injected `ITokenDictionary&`. The embedder
    /// owns its own `TokenId -> dense index` mapping because the dictionary's
    /// ids are opaque and not required to be contiguous (see the
    /// `ITokenDictionary` contract). The dictionary is built incrementally by
    /// `add_corpus_text` and `embed`. The dictionary is stable once `build`
    /// has been called: subsequent `embed` calls only add terms already
    /// present in the dictionary.
    class BowEmbedder final {
    public:
        /// \brief Convenience default constructor.
        ///
        /// Wires `BoWTokenizer` and an internal in-memory token dictionary
        /// as the default dependencies. Most existing callers (notably
        /// `BowVectorRetriever`) use this form.
        BowEmbedder();

        /// \brief Dependency-injected constructor.
        ///
        /// Tokenization is performed via `tokenizer`; term-id assignment is
        /// performed via `dictionary`. Both dependencies must outlive the
        /// embedder.
        BowEmbedder(ITokenizer& tokenizer, ITokenDictionary& dictionary);

        ~BowEmbedder();

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
        // Non-owning pointers to the active dependencies. The default
        // constructor populates these from `m_owned_tokenizer` and
        // `m_owned_dictionary`; the injected-dependency constructor points
        // them at the externally-supplied references.
        ITokenizer* m_tokenizer;
        ITokenDictionary* m_dictionary;
        // Owned defaults — populated only by the default constructor.
        // Typed by the public ITokenizer/ITokenDictionary contracts so the
        // concrete defaults (BoWTokenizer, DefaultBowTokenDictionary) stay
        // private to BowEmbedder.cpp.
        std::unique_ptr<BoWTokenizer> m_owned_tokenizer;
        std::unique_ptr<ITokenDictionary> m_owned_dictionary;
        // TokenId -> dense index in the output vector. Maintained by the
        // embedder because the dictionary's ids are opaque. Uses std::map
        // because `TokenId` does not have a `std::hash` specialization.
        std::map<TokenId, std::uint32_t> m_id_to_index;
        bool m_built = false;
    };

} // namespace agent_memory

#endif