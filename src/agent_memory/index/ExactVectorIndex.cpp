#include "ExactVectorIndex.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace agent_memory {

    namespace {

        struct ScoredRecord final {
            const VectorRecord* record = nullptr;
            float score = 0.0F;
        };

        [[nodiscard]] bool better_scored_record(
            const ScoredRecord& lhs,
            const ScoredRecord& rhs
        ) noexcept {
            if(lhs.score == rhs.score) {
                return lhs.record->chunk_id < rhs.record->chunk_id;
            }
            return lhs.score > rhs.score;
        }

    } // namespace

    ExactVectorIndex::ExactVectorIndex()
        : ExactVectorIndex(ExactVectorIndexOptions{}) {}

    ExactVectorIndex::ExactVectorIndex(ExactVectorIndexOptions options)
        : m_options(options),
          m_similarity(options.enable_simd) {}

    SimilarityMetric ExactVectorIndex::similarity_metric() const noexcept {
        return m_options.similarity_metric;
    }

    std::size_t ExactVectorIndex::dimension() const noexcept {
        return m_options.dimension;
    }

    std::size_t ExactVectorIndex::size() const noexcept {
        return m_records.size();
    }

    VectorSimilarityBackend ExactVectorIndex::similarity_backend() const noexcept {
        return m_similarity.backend();
    }

    void ExactVectorIndex::upsert(VectorRecord record) {
        validate_record_embedding(record.embedding);

        const ChunkId chunk_id = record.chunk_id;
        float inverse_norm = 0.0F;
        if(m_options.similarity_metric == SimilarityMetric::Cosine) {
            const auto squared_norm = m_similarity.squared_norm(record.embedding);
            if(squared_norm > 0.0F) {
                inverse_norm = 1.0F / std::sqrt(squared_norm);
            }
        }
        m_records.insert_or_assign(
            chunk_id,
            StoredRecord{std::move(record), inverse_norm}
        );
    }

    std::optional<VectorRecord> ExactVectorIndex::find(const ChunkId& chunk_id) const {
        const auto it = m_records.find(chunk_id);
        if(it == m_records.end()) {
            return std::nullopt;
        }
        return it->second.record;
    }

    std::vector<VectorSearchResult> ExactVectorIndex::search(
        const VectorSearchQuery& query
    ) const {
        std::vector<VectorSearchResult> results;
        if(query.limit == 0) {
            return results;
        }

        validate_query_embedding(query.embedding);

        float query_inverse_norm = 0.0F;
        if(m_options.similarity_metric == SimilarityMetric::Cosine) {
            const auto squared_norm = m_similarity.squared_norm(query.embedding);
            if(squared_norm > 0.0F) {
                query_inverse_norm = 1.0F / std::sqrt(squared_norm);
            }
        }

        std::vector<ScoredRecord> scored_records;
        scored_records.reserve(m_records.size());
        for(const auto& item : m_records) {
            const auto& stored = item.second;
            const auto& record = stored.record;
            if(!matches_metadata_filters(record.metadata, query.metadata_filters)) {
                continue;
            }

            float score = 0.0F;
            switch(m_options.similarity_metric) {
                case SimilarityMetric::Cosine:
                    if(query_inverse_norm != 0.0F && stored.inverse_norm != 0.0F) {
                        score = m_similarity.dot_product(
                            query.embedding,
                            record.embedding
                        ) * query_inverse_norm * stored.inverse_norm;
                    }
                    break;
                case SimilarityMetric::DotProduct:
                    score = m_similarity.dot_product(query.embedding, record.embedding);
                    break;
                case SimilarityMetric::Euclidean:
                    score = m_similarity.negative_squared_distance(
                        query.embedding,
                        record.embedding
                    );
                    break;
            }
            scored_records.push_back(ScoredRecord{&record, score});
        }

        if(scored_records.size() > query.limit) {
            std::partial_sort(
                scored_records.begin(),
                scored_records.begin() + static_cast<std::ptrdiff_t>(query.limit),
                scored_records.end(),
                better_scored_record
            );
            scored_records.resize(query.limit);
        } else {
            std::sort(scored_records.begin(), scored_records.end(), better_scored_record);
        }

        results.reserve(scored_records.size());
        for(const auto& scored : scored_records) {
            results.push_back(VectorSearchResult{
                scored.record->chunk_id,
                scored.score,
                scored.record->metadata
            });
        }
        return results;
    }

    bool ExactVectorIndex::erase(const ChunkId& chunk_id) {
        return m_records.erase(chunk_id) > 0;
    }

    void ExactVectorIndex::clear() {
        m_records.clear();
    }

    void ExactVectorIndex::validate_record_embedding(const Embedding& embedding) {
        if(embedding.empty()) {
            throw std::invalid_argument("ExactVectorIndex records must not use empty embeddings");
        }

        if(m_options.dimension == 0) {
            m_options.dimension = embedding.dimension();
            return;
        }

        if(embedding.dimension() != m_options.dimension) {
            throw std::invalid_argument("ExactVectorIndex record dimension mismatch");
        }
    }

    void ExactVectorIndex::validate_query_embedding(const Embedding& embedding) const {
        if(embedding.empty()) {
            throw std::invalid_argument("ExactVectorIndex queries must not use empty embeddings");
        }

        if(m_options.dimension != 0 && embedding.dimension() != m_options.dimension) {
            throw std::invalid_argument("ExactVectorIndex query dimension mismatch");
        }
    }

} // namespace agent_memory
