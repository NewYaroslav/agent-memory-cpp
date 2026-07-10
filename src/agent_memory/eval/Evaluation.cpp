#include "Evaluation.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>

namespace agent_memory {

    namespace {

        struct QueryJudgments final {
            std::map<std::string, std::int32_t> grades_by_item;
            std::vector<std::int32_t> positive_grades;
        };

        [[nodiscard]] std::size_t effective_rank(
            const RetrievalRunHit& hit,
            std::size_t position
        ) noexcept {
            if(hit.rank != 0) {
                return hit.rank;
            }
            return position + 1;
        }

        [[nodiscard]] std::map<std::string, QueryJudgments> build_judgment_map(
            const std::vector<RelevanceJudgment>& judgments
        ) {
            std::map<std::string, QueryJudgments> result;
            for(const auto& judgment : judgments) {
                if(judgment.query_id.empty() || judgment.item_id.empty()) {
                    continue;
                }
                if(judgment.relevance_grade < 0) {
                    continue;
                }

                auto& query_judgments = result[judgment.query_id];
                query_judgments.grades_by_item[judgment.item_id] =
                    judgment.relevance_grade;
            }

            for(auto& entry : result) {
                auto& positive_grades = entry.second.positive_grades;
                for(const auto& item_grade : entry.second.grades_by_item) {
                    if(item_grade.second > 0) {
                        positive_grades.push_back(item_grade.second);
                    }
                }
                std::sort(
                    positive_grades.begin(),
                    positive_grades.end(),
                    std::greater<std::int32_t>{}
                );
            }

            return result;
        }

        [[nodiscard]] std::map<std::string, const RetrievalQueryRun*> build_run_map(
            const std::vector<RetrievalQueryRun>& query_runs
        ) {
            std::map<std::string, const RetrievalQueryRun*> result;
            for(const auto& query_run : query_runs) {
                if(query_run.query_id.empty()) {
                    continue;
                }
                result[query_run.query_id] = &query_run;
            }
            return result;
        }

        [[nodiscard]] double relevance_gain(std::int32_t grade) noexcept {
            if(grade <= 0) {
                return 0.0;
            }
            return std::pow(2.0, static_cast<double>(grade)) - 1.0;
        }

        [[nodiscard]] double rank_discount(std::size_t rank) noexcept {
            if(rank == 0) {
                return 0.0;
            }
            return 1.0 / std::log2(static_cast<double>(rank) + 1.0);
        }

        [[nodiscard]] double dcg_at(
            const std::vector<RetrievalRunHit>& hits,
            const QueryJudgments& judgments,
            std::size_t cutoff
        ) {
            double result = 0.0;
            std::set<std::string> seen_items;
            for(std::size_t index = 0; index < hits.size(); ++index) {
                const auto rank = effective_rank(hits[index], index);
                if(rank == 0 || rank > cutoff) {
                    continue;
                }
                if(!seen_items.insert(hits[index].item_id).second) {
                    continue;
                }

                const auto grade_it = judgments.grades_by_item.find(hits[index].item_id);
                if(grade_it == judgments.grades_by_item.end()) {
                    continue;
                }

                result += relevance_gain(grade_it->second) * rank_discount(rank);
            }
            return result;
        }

        [[nodiscard]] double ideal_dcg_at(
            const std::vector<std::int32_t>& positive_grades,
            std::size_t cutoff
        ) {
            double result = 0.0;
            const auto count = std::min(cutoff, positive_grades.size());
            for(std::size_t index = 0; index < count; ++index) {
                const auto rank = index + 1;
                result += relevance_gain(positive_grades[index]) * rank_discount(rank);
            }
            return result;
        }

        [[nodiscard]] double recall_at(
            const std::vector<RetrievalRunHit>& hits,
            const QueryJudgments& judgments,
            std::size_t cutoff
        ) {
            if(judgments.positive_grades.empty()) {
                return 0.0;
            }

            std::size_t found = 0;
            std::set<std::string> found_items;
            for(std::size_t index = 0; index < hits.size(); ++index) {
                const auto rank = effective_rank(hits[index], index);
                if(rank == 0 || rank > cutoff) {
                    continue;
                }

                const auto grade_it = judgments.grades_by_item.find(hits[index].item_id);
                if(grade_it != judgments.grades_by_item.end() && grade_it->second > 0) {
                    if(found_items.insert(hits[index].item_id).second) {
                        ++found;
                    }
                }
            }

            return static_cast<double>(found) /
                static_cast<double>(judgments.positive_grades.size());
        }

        [[nodiscard]] double reciprocal_rank(
            const std::vector<RetrievalRunHit>& hits,
            const QueryJudgments& judgments
        ) {
            std::size_t best_rank = 0;
            for(std::size_t index = 0; index < hits.size(); ++index) {
                const auto grade_it = judgments.grades_by_item.find(hits[index].item_id);
                if(grade_it == judgments.grades_by_item.end() || grade_it->second <= 0) {
                    continue;
                }

                const auto rank = effective_rank(hits[index], index);
                if(rank == 0) {
                    continue;
                }
                if(best_rank == 0 || rank < best_rank) {
                    best_rank = rank;
                }
            }

            if(best_rank == 0) {
                return 0.0;
            }
            return 1.0 / static_cast<double>(best_rank);
        }

        [[nodiscard]] double percentile_nearest_rank(
            const std::vector<double>& sorted_values,
            double percentile
        ) noexcept {
            if(sorted_values.empty()) {
                return 0.0;
            }

            const auto raw_rank =
                std::ceil(percentile * static_cast<double>(sorted_values.size()));
            auto index = static_cast<std::size_t>(raw_rank);
            if(index == 0) {
                index = 1;
            }
            if(index > sorted_values.size()) {
                index = sorted_values.size();
            }
            return sorted_values[index - 1];
        }

        [[nodiscard]] LatencyStats compute_latency_stats(std::vector<double> values) {
            LatencyStats stats;
            stats.sample_count = values.size();
            if(values.empty()) {
                return stats;
            }

            std::sort(values.begin(), values.end());
            stats.mean = std::accumulate(values.begin(), values.end(), 0.0) /
                static_cast<double>(values.size());
            stats.p50 = percentile_nearest_rank(values, 0.50);
            stats.p95 = percentile_nearest_rank(values, 0.95);
            stats.p99 = percentile_nearest_rank(values, 0.99);
            return stats;
        }

    } // namespace

    bool EvalCorpusItem::empty() const noexcept {
        return id.empty() && title.empty() && text.empty();
    }

    bool EvalQuery::empty() const noexcept {
        return id.empty() && text.empty();
    }

    bool RelevanceJudgment::relevant() const noexcept {
        return relevance_grade > 0;
    }

    bool RetrievalEvalDataset::empty() const noexcept {
        return queries.empty();
    }

    std::size_t RetrievalEvalDataset::query_count() const noexcept {
        return queries.size();
    }

    bool RetrievalQueryRun::empty() const noexcept {
        return hits.empty();
    }

    bool RetrievalRun::empty() const noexcept {
        return queries.empty();
    }

    std::optional<double> metric_value_at(
        const std::vector<MetricAtK>& metrics,
        std::size_t k
    ) noexcept {
        for(const auto& metric : metrics) {
            if(metric.k == k) {
                return metric.value;
            }
        }
        return std::nullopt;
    }

    RetrievalMetrics evaluate_retrieval(
        const RetrievalEvalDataset& dataset,
        const RetrievalRun& run,
        const RetrievalEvaluationOptions& options
    ) {
        RetrievalMetrics metrics;
        metrics.query_count = dataset.queries.size();

        for(const auto cutoff : options.recall_cutoffs) {
            metrics.recall_at.push_back(MetricAtK{cutoff, 0.0});
        }
        for(const auto cutoff : options.ndcg_cutoffs) {
            metrics.ndcg_at.push_back(MetricAtK{cutoff, 0.0});
        }

        const auto judgments_by_query = build_judgment_map(dataset.judgments);
        const auto runs_by_query = build_run_map(run.queries);

        std::vector<double> latency_values;
        std::size_t no_answer_correct = 0;

        for(const auto& query : dataset.queries) {
            const auto judgments_it = judgments_by_query.find(query.id);
            const QueryJudgments* query_judgments = nullptr;
            if(judgments_it != judgments_by_query.end()) {
                query_judgments = &judgments_it->second;
            }

            const auto run_it = runs_by_query.find(query.id);
            const RetrievalQueryRun* query_run = nullptr;
            if(run_it != runs_by_query.end()) {
                query_run = run_it->second;
                if(query_run->latency_ms) {
                    latency_values.push_back(*query_run->latency_ms);
                }
            }

            const std::vector<RetrievalRunHit> empty_hits;
            const auto& hits = query_run != nullptr ? query_run->hits : empty_hits;

            if(query_judgments == nullptr || query_judgments->positive_grades.empty()) {
                ++metrics.no_answer_query_count;
                if(hits.empty()) {
                    ++no_answer_correct;
                }
                continue;
            }

            ++metrics.judged_query_count;
            for(auto& metric : metrics.recall_at) {
                metric.value += recall_at(hits, *query_judgments, metric.k);
            }

            for(auto& metric : metrics.ndcg_at) {
                const auto ideal = ideal_dcg_at(query_judgments->positive_grades, metric.k);
                if(ideal > 0.0) {
                    metric.value += dcg_at(hits, *query_judgments, metric.k) / ideal;
                }
            }

            metrics.mrr += reciprocal_rank(hits, *query_judgments);
        }

        if(metrics.judged_query_count != 0) {
            const auto denominator = static_cast<double>(metrics.judged_query_count);
            for(auto& metric : metrics.recall_at) {
                metric.value /= denominator;
            }
            for(auto& metric : metrics.ndcg_at) {
                metric.value /= denominator;
            }
            metrics.mrr /= denominator;
        }

        if(metrics.no_answer_query_count != 0) {
            metrics.no_answer_accuracy =
                static_cast<double>(no_answer_correct) /
                static_cast<double>(metrics.no_answer_query_count);
        }

        metrics.latency_ms = compute_latency_stats(std::move(latency_values));
        return metrics;
    }

} // namespace agent_memory
