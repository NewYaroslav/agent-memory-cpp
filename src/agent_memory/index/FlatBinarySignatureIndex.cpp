#include "FlatBinarySignatureIndex.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace agent_memory {

    FlatBinarySignatureIndex::FlatBinarySignatureIndex()
        : FlatBinarySignatureIndex(FlatBinarySignatureIndexOptions{}) {}

    FlatBinarySignatureIndex::FlatBinarySignatureIndex(
        FlatBinarySignatureIndexOptions options
    )
        : m_options(std::move(options)) {
        if(m_options.signature_info && !is_valid(*m_options.signature_info)) {
            throw std::invalid_argument(
                "FlatBinarySignatureIndex configured signature identity must be valid"
            );
        }
        if(m_options.signature_info) {
            m_distance.emplace(binary_signature_word_count(bit_count()));
        }
    }

    std::size_t FlatBinarySignatureIndex::bit_count() const noexcept {
        if(!m_options.signature_info) {
            return 0;
        }
        return m_options.signature_info->bit_count;
    }

    std::size_t FlatBinarySignatureIndex::size() const noexcept {
        return m_records.size();
    }

    std::optional<HammingDistanceBackend> FlatBinarySignatureIndex::hamming_backend() const noexcept {
        if(!m_distance) {
            return std::nullopt;
        }
        return m_distance->backend();
    }

    void FlatBinarySignatureIndex::upsert(BinarySignatureRecord record) {
        validate_record(record);

        const ChunkId chunk_id = record.chunk_id;
        const auto existing = m_positions.find(chunk_id);
        if(existing != m_positions.end()) {
            const auto position = existing->second;
            std::copy(
                record.signature.words().begin(),
                record.signature.words().end(),
                signature_words(position)
            );
            m_records[position].metadata = std::move(record.metadata);
            return;
        }

        const auto position = m_records.size();
        m_positions.emplace(chunk_id, position);
        try {
            m_records.push_back(StoredRecord{chunk_id, std::move(record.metadata)});
            m_signature_words.insert(
                m_signature_words.end(),
                record.signature.words().begin(),
                record.signature.words().end()
            );
        } catch(...) {
            if(m_records.size() > position) {
                m_records.pop_back();
            }
            m_positions.erase(chunk_id);
            throw;
        }
    }

    std::optional<BinarySignatureRecord> FlatBinarySignatureIndex::find(
        const ChunkId& chunk_id
    ) const {
        const auto it = m_positions.find(chunk_id);
        if(it == m_positions.end()) {
            return std::nullopt;
        }

        const auto position = it->second;
        const auto* words = signature_words(position);
        std::vector<std::uint64_t> signature(
            words,
            words + static_cast<std::ptrdiff_t>(m_distance->word_count())
        );
        return BinarySignatureRecord{
            m_records[position].chunk_id,
            BinarySignature(bit_count(), std::move(signature)),
            *m_options.signature_info,
            m_records[position].metadata
        };
    }

    std::vector<BinarySignatureSearchResult> FlatBinarySignatureIndex::search(
        const BinarySignatureSearchQuery& query
    ) const {
        std::vector<BinarySignatureSearchResult> results;
        if(query.limit == 0) {
            return results;
        }

        validate_query(query);

        if(m_records.empty()) {
            return results;
        }

        std::vector<std::size_t> distances(m_records.size());
        m_distance->compute_distances(
            query.signature.words().data(),
            m_signature_words.data(),
            m_records.size(),
            distances.data()
        );

        std::vector<std::size_t> distance_counts(bit_count() + 1, 0);
        std::size_t matched_count = 0;
        std::vector<unsigned char> filter_matches;
        if(!query.metadata_filters.empty()) {
            filter_matches.resize(m_records.size(), 0);
        }
        for(std::size_t position = 0; position < m_records.size(); ++position) {
            const auto& record = m_records[position];
            if(!query.metadata_filters.empty()
               && !matches_metadata_filters(record.metadata, query.metadata_filters)) {
                continue;
            }
            if(!filter_matches.empty()) {
                filter_matches[position] = 1;
            }
            ++distance_counts[distances[position]];
            ++matched_count;
        }

        if(matched_count == 0) {
            return results;
        }

        const auto selected_count = std::min(query.limit, matched_count);
        std::size_t cutoff_distance = 0;
        std::size_t cumulative_count = 0;
        for(; cutoff_distance < distance_counts.size(); ++cutoff_distance) {
            cumulative_count += distance_counts[cutoff_distance];
            if(cumulative_count >= selected_count) {
                break;
            }
        }

        struct ScoredRecord final {
            const StoredRecord* record = nullptr;
            std::size_t hamming_distance = 0;
        };
        const auto closer_binary_record = [](const ScoredRecord& lhs, const ScoredRecord& rhs) {
            if(lhs.hamming_distance == rhs.hamming_distance) {
                return lhs.record->chunk_id < rhs.record->chunk_id;
            }
            return lhs.hamming_distance < rhs.hamming_distance;
        };

        std::vector<ScoredRecord> scored_records;
        scored_records.reserve(cumulative_count);
        for(std::size_t position = 0; position < m_records.size(); ++position) {
            const auto& record = m_records[position];
            if(distances[position] > cutoff_distance
               || (!filter_matches.empty() && filter_matches[position] == 0)) {
                continue;
            }
            scored_records.push_back(ScoredRecord{&record, distances[position]});
        }

        std::sort(scored_records.begin(), scored_records.end(), closer_binary_record);
        scored_records.resize(selected_count);

        results.reserve(scored_records.size());
        for(const auto& scored : scored_records) {
            results.push_back(BinarySignatureSearchResult{
                scored.record->chunk_id,
                scored.hamming_distance,
                scored.record->metadata
            });
        }
        return results;
    }

    bool FlatBinarySignatureIndex::erase(const ChunkId& chunk_id) {
        const auto existing = m_positions.find(chunk_id);
        if(existing == m_positions.end()) {
            return false;
        }

        const auto position = existing->second;
        const auto last_position = m_records.size() - 1;
        if(position != last_position) {
            m_records[position] = std::move(m_records[last_position]);
            std::copy(
                signature_words(last_position),
                signature_words(last_position) + static_cast<std::ptrdiff_t>(m_distance->word_count()),
                signature_words(position)
            );
            m_positions.find(m_records[position].chunk_id)->second = position;
        }

        m_records.pop_back();
        m_signature_words.resize(m_records.size() * m_distance->word_count());
        m_positions.erase(existing);
        return true;
    }

    void FlatBinarySignatureIndex::clear() {
        m_records.clear();
        m_signature_words.clear();
        m_positions.clear();
    }

    const std::uint64_t* FlatBinarySignatureIndex::signature_words(
        std::size_t position
    ) const noexcept {
        return m_signature_words.data() + position * m_distance->word_count();
    }

    std::uint64_t* FlatBinarySignatureIndex::signature_words(std::size_t position) noexcept {
        return m_signature_words.data() + position * m_distance->word_count();
    }

    void FlatBinarySignatureIndex::validate_record_signature(
        const BinarySignature& signature
    ) {
        if(signature.empty()) {
            throw std::invalid_argument(
                "FlatBinarySignatureIndex records must not use empty signatures"
            );
        }

        if(signature.bit_count() != bit_count()) {
            throw std::invalid_argument(
                "FlatBinarySignatureIndex record bit-count mismatch"
            );
        }
    }

    void FlatBinarySignatureIndex::validate_query_signature(
        const BinarySignature& signature
    ) const {
        if(signature.empty()) {
            throw std::invalid_argument(
                "FlatBinarySignatureIndex queries must not use empty signatures"
            );
        }

        if(m_options.signature_info && signature.bit_count() != bit_count()) {
            throw std::invalid_argument(
                "FlatBinarySignatureIndex query bit-count mismatch"
            );
        }
    }

    void FlatBinarySignatureIndex::validate_record(BinarySignatureRecord& record) {
        if(!is_valid(record.signature_info)) {
            throw std::invalid_argument(
                "FlatBinarySignatureIndex records must use valid signature identity"
            );
        }
        if(record.signature.bit_count() != record.signature_info.bit_count) {
            throw std::invalid_argument(
                "FlatBinarySignatureIndex record signature width must match identity"
            );
        }

        if(!m_options.signature_info) {
            m_options.signature_info = record.signature_info;
            m_distance.emplace(binary_signature_word_count(bit_count()));
        }

        validate_record_signature(record.signature);
        require_matching_identity(record.signature_info);
    }

    void FlatBinarySignatureIndex::validate_query(
        const BinarySignatureSearchQuery& query
    ) const {
        if(!is_valid(query.signature_info)) {
            throw std::invalid_argument(
                "FlatBinarySignatureIndex queries must use valid signature identity"
            );
        }
        if(query.signature.bit_count() != query.signature_info.bit_count) {
            throw std::invalid_argument(
                "FlatBinarySignatureIndex query signature width must match identity"
            );
        }

        validate_query_signature(query.signature);
        require_matching_identity(query.signature_info);
    }

    void FlatBinarySignatureIndex::require_matching_identity(
        const BinarySignatureInfo& info
    ) const {
        if(m_options.signature_info && info != *m_options.signature_info) {
            throw std::invalid_argument(
                "FlatBinarySignatureIndex binary signature identity mismatch"
            );
        }
    }

} // namespace agent_memory
