#pragma once
#ifndef AGENT_MEMORY_HEADER_RETRIEVAL_BRUTE_FORCE_TOP_K_INDEX_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_RETRIEVAL_BRUTE_FORCE_TOP_K_INDEX_HPP_INCLUDED

/// \file BruteForceTopKIndex.hpp
/// \brief Dependency-free brute-force top-K vector index.
///
/// Storage is a flat `std::vector<std::pair<std::string, std::vector<float>>>`.
/// Top-K is computed via `std::partial_sort` over an index array, so the
/// full matrix is never copied. Cosine similarity is computed on demand
/// from L2-normalized input vectors (the BowEmbedder emits normalized
/// vectors so dot product == cosine here, but the implementation falls
/// back to a true cosine ratio to remain correct for non-normalized
/// callers).
///
/// The `build()` method is a no-op for brute force: it exists so the
/// caller-visible lifecycle matches the planned ANN indices (HNSW, IVF,
/// etc.) without requiring API changes when ANN backends replace this
/// class in later PRs.
///
/// \note Named `BruteForceTopKIndex` to avoid clashing with the
///       `agent_memory::ExactVectorIndex` in `agent_memory/index/`, which
///       implements the richer `IVectorIndex` interface over `Embedding`.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace agent_memory {

    /// \brief Brute-force top-K cosine similarity index over dense float vectors.
    class BruteForceTopKIndex final {
    public:
        /// \brief Empty index with no records.
        BruteForceTopKIndex();

        /// \brief Returns the number of stored records.
        [[nodiscard]] std::size_t size() const noexcept;

        /// \brief Returns the configured vector dimensionality (0 until add()).
        [[nodiscard]] std::size_t dimension() const noexcept;

        /// \brief Inserts a record. Throws if `vector.size()` does not match
        ///        the configured dimension once one has been adopted.
        void add(std::string doc_id, std::vector<float> vector);

        /// \brief Finalizes the index. No-op for brute force; reserved so
        ///        future ANN backends can plug in without API churn.
        void build();

        /// \brief Returns the top-`k` records ranked by cosine similarity
        ///        against `query`. Empty vectors, zero-length queries, or
        ///        `k == 0` produce an empty result. Zero-norm documents are
        ///        skipped (no match).
        [[nodiscard]] std::vector<std::pair<std::string, double>> top_k(
            const std::vector<float>& query,
            std::size_t k
        ) const;

    private:
        std::vector<std::pair<std::string, std::vector<float>>> m_records;
        std::size_t m_dimension = 0;
    };

} // namespace agent_memory

#endif