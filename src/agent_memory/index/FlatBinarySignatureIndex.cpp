#include "FlatBinarySignatureIndex.hpp"

#include <algorithm>
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

    void FlatBinarySignatureIndex::upsert(BinarySignatureRecord record) {
        validate_record(record);

        const ChunkId chunk_id = record.chunk_id;
        m_records[chunk_id] = std::move(record);
    }

    std::optional<BinarySignatureRecord> FlatBinarySignatureIndex::find(
        const ChunkId& chunk_id
    ) const {
        const auto it = m_records.find(chunk_id);
        if(it == m_records.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<BinarySignatureSearchResult> FlatBinarySignatureIndex::search(
        const BinarySignatureSearchQuery& query
    ) const {
        std::vector<BinarySignatureSearchResult> results;
        if(query.limit == 0) {
            return results;
        }

        validate_query(query);

        for(const auto& item : m_records) {
            const auto& record = item.second;
            if(!matches_metadata_filters(record.metadata, query.metadata_filters)) {
                continue;
            }

            results.push_back(BinarySignatureSearchResult{
                record.chunk_id,
                hamming_distance(query.signature, record.signature),
                record.metadata
            });
        }

        std::sort(
            results.begin(),
            results.end(),
            [](const auto& lhs, const auto& rhs) {
                if(lhs.hamming_distance == rhs.hamming_distance) {
                    return lhs.chunk_id < rhs.chunk_id;
                }
                return lhs.hamming_distance < rhs.hamming_distance;
            }
        );

        if(results.size() > query.limit) {
            results.resize(query.limit);
        }
        return results;
    }

    bool FlatBinarySignatureIndex::erase(const ChunkId& chunk_id) {
        return m_records.erase(chunk_id) > 0;
    }

    void FlatBinarySignatureIndex::clear() {
        m_records.clear();
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
