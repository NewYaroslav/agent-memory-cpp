#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_FLAT_BINARY_SIGNATURE_INDEX_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_FLAT_BINARY_SIGNATURE_INDEX_HPP_INCLUDED

/// \file FlatBinarySignatureIndex.hpp
/// \brief In-memory exact Hamming binary-signature index implementation.

#include "IBinarySignatureIndex.hpp"

#include <map>
#include <optional>
#include <vector>

namespace agent_memory {

    /// \brief Options used to create an exact in-memory binary-signature index.
    struct FlatBinarySignatureIndexOptions final {
        /// \brief Binary-space identity accepted by this index.
        ///
        /// Empty identity means the index adopts the first inserted record's
        /// identity and then enforces it for later records and queries.
        std::optional<BinarySignatureInfo> signature_info;
    };

    /// \brief Deterministic in-memory exact binary-signature index.
    ///
    /// This implementation scans all stored signatures, computes exact Hamming
    /// distance, and returns the closest records. It is an oracle baseline for
    /// validating future bucket/ANN indexes, not a sub-linear production index.
    class FlatBinarySignatureIndex final : public IBinarySignatureIndex {
    public:
        FlatBinarySignatureIndex();
        explicit FlatBinarySignatureIndex(FlatBinarySignatureIndexOptions options);

        [[nodiscard]] std::size_t bit_count() const noexcept override;
        [[nodiscard]] std::size_t size() const noexcept override;

        /// \brief Hamming backend selected for the configured signature width.
        [[nodiscard]] std::optional<HammingDistanceBackend> hamming_backend() const noexcept;

        void upsert(BinarySignatureRecord record) override;

        [[nodiscard]] std::optional<BinarySignatureRecord> find(
            const ChunkId& chunk_id
        ) const override;

        /// \brief Searches all stored binary signatures exactly.
        /// \note Results are ordered by ascending Hamming distance. Ties are
        ///       broken deterministically by chunk id ascending.
        [[nodiscard]] std::vector<BinarySignatureSearchResult> search(
            const BinarySignatureSearchQuery& query
        ) const override;

        [[nodiscard]] bool erase(const ChunkId& chunk_id) override;

        /// \brief Removes all records without changing the configured or adopted bit count.
        void clear() override;

    private:
        struct StoredRecord final {
            ChunkId chunk_id;
            Metadata metadata;
        };

        [[nodiscard]] const std::uint64_t* signature_words(std::size_t position) const noexcept;
        [[nodiscard]] std::uint64_t* signature_words(std::size_t position) noexcept;
        void validate_record_signature(const BinarySignature& signature);
        void validate_query_signature(const BinarySignature& signature) const;
        void validate_record(BinarySignatureRecord& record);
        void validate_query(const BinarySignatureSearchQuery& query) const;
        void require_matching_identity(const BinarySignatureInfo& info) const;

        FlatBinarySignatureIndexOptions m_options;
        std::vector<StoredRecord> m_records;
        std::vector<std::uint64_t> m_signature_words;
        std::map<ChunkId, std::size_t> m_positions;
        std::optional<HammingDistanceComputer> m_distance;
    };

} // namespace agent_memory

#endif
