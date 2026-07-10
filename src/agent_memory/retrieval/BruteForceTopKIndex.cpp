#include "BruteForceTopKIndex.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace agent_memory {

    BruteForceTopKIndex::BruteForceTopKIndex() = default;

    std::size_t BruteForceTopKIndex::size() const noexcept {
        return m_records.size();
    }

    std::size_t BruteForceTopKIndex::dimension() const noexcept {
        return m_dimension;
    }

    void BruteForceTopKIndex::add(std::string doc_id, std::vector<float> vector) {
        if(m_dimension == 0) {
            m_dimension = vector.size();
        } else if(vector.size() != m_dimension) {
            throw std::invalid_argument(
                "BruteForceTopKIndex: vector dimension mismatch"
            );
        }
        m_records.emplace_back(std::move(doc_id), std::move(vector));
    }

    void BruteForceTopKIndex::build() {
        // No-op for brute force. Reserved for future ANN backends.
    }

    std::vector<std::pair<std::string, double>> BruteForceTopKIndex::top_k(
        const std::vector<float>& query,
        std::size_t k
    ) const {
        std::vector<std::pair<std::string, double>> result;
        if(k == 0 || m_records.empty() || query.empty()) {
            return result;
        }
        if(m_dimension != 0 && query.size() != m_dimension) {
            throw std::invalid_argument(
                "BruteForceTopKIndex: query dimension mismatch"
            );
        }

        // Pre-compute the query norm once; the index assumes callers pass
        // L2-normalized vectors from BowEmbedder, but we compute the true
        // cosine ratio defensively for non-normalized callers. A zero-norm
        // query is allowed to fall through so that purely out-of-vocabulary
        // queries still receive a deterministic tie-broken ranking; the
        // per-document scoring branch produces cosine == 0 in that case.
        double q_norm_sq = 0.0;
        for(const float v : query) {
            q_norm_sq += static_cast<double>(v) * static_cast<double>(v);
        }

        // partial_sort over an index array: zero copies of the full matrix.
        std::vector<std::size_t> order(m_records.size());
        for(std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        const std::size_t limit = std::min(k, m_records.size());
        std::partial_sort(
            order.begin(),
            order.begin() + static_cast<std::ptrdiff_t>(limit),
            order.end(),
            [&](std::size_t lhs, std::size_t rhs) {
                const auto& a = m_records[lhs].second;
                const auto& b = m_records[rhs].second;
                double a_dot = 0.0;
                double a_norm_sq = 0.0;
                double b_dot = 0.0;
                double b_norm_sq = 0.0;
                const std::size_t n = query.size();
                for(std::size_t i = 0; i < n; ++i) {
                    const float qv = query[i];
                    a_dot += static_cast<double>(qv) * static_cast<double>(a[i]);
                    a_norm_sq += static_cast<double>(a[i]) * static_cast<double>(a[i]);
                    b_dot += static_cast<double>(qv) * static_cast<double>(b[i]);
                    b_norm_sq += static_cast<double>(b[i]) * static_cast<double>(b[i]);
                }
                // Higher cosine first; ties broken by doc_id ascending so
                // the result is byte-deterministic across runs. A zero-norm
                // query or zero-norm document yields score 0, so the tie
                // break resolves to the ascending doc_id ordering.
                const double a_score = (a_norm_sq > 0.0 && q_norm_sq > 0.0)
                    ? (a_dot / std::sqrt(a_norm_sq * q_norm_sq))
                    : 0.0;
                const double b_score = (b_norm_sq > 0.0 && q_norm_sq > 0.0)
                    ? (b_dot / std::sqrt(b_norm_sq * q_norm_sq))
                    : 0.0;
                if(a_score == b_score) {
                    return m_records[lhs].first < m_records[rhs].first;
                }
                return a_score > b_score;
            }
        );

        result.reserve(limit);
        for(std::size_t i = 0; i < limit; ++i) {
            const auto& record = m_records[order[i]];
            const auto& vec = record.second;
            double dot = 0.0;
            double v_norm_sq = 0.0;
            for(std::size_t j = 0; j < query.size(); ++j) {
                dot += static_cast<double>(query[j]) * static_cast<double>(vec[j]);
                v_norm_sq += static_cast<double>(vec[j]) * static_cast<double>(vec[j]);
            }
            const double score = (v_norm_sq > 0.0 && q_norm_sq > 0.0)
                ? (dot / std::sqrt(v_norm_sq * q_norm_sq))
                : 0.0;
            result.emplace_back(record.first, score);
        }
        return result;
    }

} // namespace agent_memory