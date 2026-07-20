#include "ExactVectorIndex.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace agent_memory {

    namespace {

        [[nodiscard]] float dot_product(const Embedding& lhs, const Embedding& rhs) {
            float score = 0.0F;
            for(std::size_t i = 0; i < lhs.values.size(); ++i) {
                score += lhs.values[i] * rhs.values[i];
            }
            return score;
        }

        [[nodiscard]] float cosine_similarity(const Embedding& lhs, const Embedding& rhs) {
            float dot = 0.0F;
            float lhs_norm = 0.0F;
            float rhs_norm = 0.0F;
            for(std::size_t i = 0; i < lhs.values.size(); ++i) {
                dot += lhs.values[i] * rhs.values[i];
                lhs_norm += lhs.values[i] * lhs.values[i];
                rhs_norm += rhs.values[i] * rhs.values[i];
            }

            if(lhs_norm == 0.0F || rhs_norm == 0.0F) {
                return 0.0F;
            }

            return dot / std::sqrt(lhs_norm * rhs_norm);
        }

        [[nodiscard]] float negative_squared_distance(const Embedding& lhs, const Embedding& rhs) {
            float distance = 0.0F;
            for(std::size_t i = 0; i < lhs.values.size(); ++i) {
                const float delta = lhs.values[i] - rhs.values[i];
                distance += delta * delta;
            }
            return -distance;
        }

        [[nodiscard]] float score_embedding(
            SimilarityMetric metric,
            const Embedding& query,
            const Embedding& candidate
        ) {
            switch(metric) {
                case SimilarityMetric::Cosine:
                    return cosine_similarity(query, candidate);
                case SimilarityMetric::DotProduct:
                    return dot_product(query, candidate);
                case SimilarityMetric::Euclidean:
                    return negative_squared_distance(query, candidate);
            }
            return 0.0F;
        }

        [[nodiscard]] bool better_vector_result(
            const VectorSearchResult& lhs,
            const VectorSearchResult& rhs
        ) noexcept {
            if(lhs.score == rhs.score) {
                return lhs.chunk_id < rhs.chunk_id;
            }
            return lhs.score > rhs.score;
        }

    } // namespace

    ExactVectorIndex::ExactVectorIndex()
        : ExactVectorIndex(ExactVectorIndexOptions{}) {}

    ExactVectorIndex::ExactVectorIndex(ExactVectorIndexOptions options)
        : m_options(options) {}

    SimilarityMetric ExactVectorIndex::similarity_metric() const noexcept {
        return m_options.similarity_metric;
    }

    std::size_t ExactVectorIndex::dimension() const noexcept {
        return m_options.dimension;
    }

    std::size_t ExactVectorIndex::size() const noexcept {
        return m_records.size();
    }

    void ExactVectorIndex::upsert(VectorRecord record) {
        validate_record_embedding(record.embedding);

        const ChunkId chunk_id = record.chunk_id;
        m_records[chunk_id] = std::move(record);
    }

    std::optional<VectorRecord> ExactVectorIndex::find(const ChunkId& chunk_id) const {
        const auto it = m_records.find(chunk_id);
        if(it == m_records.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<VectorSearchResult> ExactVectorIndex::search(
        const VectorSearchQuery& query
    ) const {
        std::vector<VectorSearchResult> results;
        if(query.limit == 0) {
            return results;
        }

        validate_query_embedding(query.embedding);

        for(const auto& item : m_records) {
            const auto& record = item.second;
            if(!matches_metadata_filters(record.metadata, query.metadata_filters)) {
                continue;
            }

            results.push_back(VectorSearchResult{
                record.chunk_id,
                score_embedding(m_options.similarity_metric, query.embedding, record.embedding),
                record.metadata
            });
        }

        if(results.size() > query.limit) {
            std::partial_sort(
                results.begin(),
                results.begin() + static_cast<std::ptrdiff_t>(query.limit),
                results.end(),
                better_vector_result
            );
            results.resize(query.limit);
        } else {
            std::sort(results.begin(), results.end(), better_vector_result);
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
