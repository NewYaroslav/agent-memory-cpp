#include "Evaluation.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

namespace agent_memory {

    namespace {

        /// \brief Relevance judgments grouped for one query.
        struct QueryJudgments final {
            std::map<std::string, std::int32_t> grades_by_item;
            std::vector<std::int32_t> positive_grades;
        };

        using QueryModes = std::map<std::string, EvalQueryAnswerMode>;

        /// \brief Builds query-id mode map and rejects ambiguous query ids.
        [[nodiscard]] QueryModes build_query_modes(
            const std::vector<EvalQuery>& queries
        ) {
            QueryModes result;
            for(const auto& query : queries) {
                if(query.id.empty()) {
                    throw std::invalid_argument("evaluation query id must not be empty");
                }
                const auto inserted = result.emplace(query.id, query.answer_mode);
                if(!inserted.second) {
                    throw std::invalid_argument("duplicate evaluation query id");
                }
            }
            return result;
        }

        /// \brief Builds a query-id to relevance-judgment map and sorted ideal grades.
        ///
        /// Positive grades are sorted descending once so nDCG can compute IDCG cheaply.
        [[nodiscard]] std::map<std::string, QueryJudgments> build_judgment_map(
            const std::vector<RelevanceJudgment>& judgments,
            const QueryModes& query_modes
        ) {
            std::map<std::string, QueryJudgments> result;
            for(const auto& judgment : judgments) {
                if(judgment.query_id.empty() || judgment.item_id.empty()) {
                    throw std::invalid_argument(
                        "relevance judgment query_id and item_id must not be empty"
                    );
                }
                if(judgment.relevance_grade < 0) {
                    throw std::invalid_argument(
                        "relevance judgment grade must not be negative"
                    );
                }
                if(query_modes.find(judgment.query_id) == query_modes.end()) {
                    throw std::invalid_argument(
                        "relevance judgment references an unknown query id"
                    );
                }

                auto& query_judgments = result[judgment.query_id];
                if(
                    query_judgments.grades_by_item.find(judgment.item_id) !=
                    query_judgments.grades_by_item.end()
                ) {
                    throw std::invalid_argument(
                        "duplicate relevance judgment for query/item pair"
                    );
                }
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

        /// \brief Builds a query-id to retrieval-run map.
        [[nodiscard]] std::map<std::string, const RetrievalQueryRun*> build_run_map(
            const std::vector<RetrievalQueryRun>& query_runs,
            const QueryModes& query_modes
        ) {
            std::map<std::string, const RetrievalQueryRun*> result;
            for(const auto& query_run : query_runs) {
                if(query_run.query_id.empty()) {
                    throw std::invalid_argument("retrieval run query_id must not be empty");
                }
                if(query_modes.find(query_run.query_id) == query_modes.end()) {
                    throw std::invalid_argument(
                        "retrieval run references an unknown query id"
                    );
                }
                if(query_run.latency_ms) {
                    const auto latency = *query_run.latency_ms;
                    if(!std::isfinite(latency) || latency < 0.0) {
                        throw std::invalid_argument(
                            "retrieval run latency_ms must be finite and non-negative"
                        );
                    }
                }
                const auto inserted = result.emplace(query_run.query_id, &query_run);
                if(!inserted.second) {
                    throw std::invalid_argument("duplicate retrieval run query id");
                }
            }
            return result;
        }

        /// \brief Hit paired with the rank used by metric formulas.
        struct RankedHit final {
            RetrievalRunHit hit;
            std::size_t metric_rank = 0;
        };

        /// \brief Returns hits in metric order and validates rank/score consistency.
        [[nodiscard]] std::vector<RankedHit> normalize_hits(
            const std::vector<RetrievalRunHit>& hits
        ) {
            bool has_explicit_rank = false;
            bool has_implicit_rank = false;
            std::set<std::size_t> explicit_ranks;

            for(const auto& hit : hits) {
                if(hit.item_id.empty()) {
                    throw std::invalid_argument("retrieval hit item_id must not be empty");
                }
                if(!std::isfinite(static_cast<double>(hit.score))) {
                    throw std::invalid_argument("retrieval hit score must be finite");
                }
                if(hit.rank == 0) {
                    has_implicit_rank = true;
                } else {
                    has_explicit_rank = true;
                    if(!explicit_ranks.insert(hit.rank).second) {
                        throw std::invalid_argument(
                            "duplicate explicit retrieval hit rank"
                        );
                    }
                }
            }

            if(has_explicit_rank && has_implicit_rank) {
                throw std::invalid_argument(
                    "retrieval hits must not mix explicit and implicit ranks"
                );
            }

            std::vector<RankedHit> result;
            result.reserve(hits.size());
            if(has_explicit_rank) {
                for(const auto& hit : hits) {
                    result.push_back(RankedHit{hit, hit.rank});
                }
                std::sort(
                    result.begin(),
                    result.end(),
                    [](const RankedHit& lhs, const RankedHit& rhs) {
                        if(lhs.metric_rank != rhs.metric_rank) {
                            return lhs.metric_rank < rhs.metric_rank;
                        }
                        return lhs.hit.item_id < rhs.hit.item_id;
                    }
                );
            } else {
                for(std::size_t index = 0; index < hits.size(); ++index) {
                    result.push_back(RankedHit{hits[index], index + 1});
                }
            }

            return result;
        }

        /// \brief Validates all query runs and caches normalized hit order.
        [[nodiscard]] std::map<std::string, std::vector<RankedHit>> build_hit_map(
            const std::map<std::string, const RetrievalQueryRun*>& runs_by_query
        ) {
            std::map<std::string, std::vector<RankedHit>> result;
            for(const auto& entry : runs_by_query) {
                result.emplace(entry.first, normalize_hits(entry.second->hits));
            }
            return result;
        }

        /// \brief Rejects nonsensical metric cutoffs.
        void validate_cutoffs(const RetrievalEvaluationOptions& options) {
            for(const auto cutoff : options.recall_cutoffs) {
                if(cutoff == 0) {
                    throw std::invalid_argument("recall cutoff must be greater than zero");
                }
            }
            for(const auto cutoff : options.ndcg_cutoffs) {
                if(cutoff == 0) {
                    throw std::invalid_argument("nDCG cutoff must be greater than zero");
                }
            }
        }

        /// \brief Converts a graded relevance label to DCG gain.
        [[nodiscard]] double relevance_gain(std::int32_t grade) noexcept {
            if(grade <= 0) {
                return 0.0;
            }
            return std::pow(2.0, static_cast<double>(grade)) - 1.0;
        }

        /// \brief Returns the logarithmic rank discount used by DCG/nDCG.
        [[nodiscard]] double rank_discount(std::size_t rank) noexcept {
            if(rank == 0) {
                return 0.0;
            }
            return 1.0 / std::log2(static_cast<double>(rank) + 1.0);
        }

        /// \brief Computes DCG@K for one query run.
        ///
        /// Duplicate item ids are counted only once so fusion bugs cannot
        /// inflate graded relevance metrics.
        [[nodiscard]] double dcg_at(
            const std::vector<RankedHit>& hits,
            const QueryJudgments& judgments,
            std::size_t cutoff
        ) {
            double result = 0.0;
            std::set<std::string> seen_items;
            for(const auto& hit : hits) {
                const auto rank = hit.metric_rank;
                if(rank == 0 || rank > cutoff) {
                    continue;
                }
                if(!seen_items.insert(hit.hit.item_id).second) {
                    continue;
                }

                const auto grade_it = judgments.grades_by_item.find(hit.hit.item_id);
                if(grade_it == judgments.grades_by_item.end()) {
                    continue;
                }

                result += relevance_gain(grade_it->second) * rank_discount(rank);
            }
            return result;
        }

        /// \brief Computes the ideal DCG@K from sorted positive relevance grades.
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

        /// \brief Computes Recall@K for one query run.
        ///
        /// Duplicate relevant hits are counted once. This keeps Recall@K stable
        /// for hybrid/fused runs that accidentally emit the same item twice.
        [[nodiscard]] double recall_at(
            const std::vector<RankedHit>& hits,
            const QueryJudgments& judgments,
            std::size_t cutoff
        ) {
            if(judgments.positive_grades.empty()) {
                return 0.0;
            }

            std::size_t found = 0;
            std::set<std::string> found_items;
            for(const auto& hit : hits) {
                const auto rank = hit.metric_rank;
                if(rank == 0 || rank > cutoff) {
                    continue;
                }

                const auto grade_it = judgments.grades_by_item.find(hit.hit.item_id);
                if(grade_it != judgments.grades_by_item.end() && grade_it->second > 0) {
                    if(found_items.insert(hit.hit.item_id).second) {
                        ++found;
                    }
                }
            }

            return static_cast<double>(found) /
                static_cast<double>(judgments.positive_grades.size());
        }

        /// \brief Computes reciprocal rank of the first relevant hit.
        [[nodiscard]] double reciprocal_rank(
            const std::vector<RankedHit>& hits,
            const QueryJudgments& judgments
        ) {
            std::size_t best_rank = 0;
            for(const auto& hit : hits) {
                const auto grade_it = judgments.grades_by_item.find(hit.hit.item_id);
                if(grade_it == judgments.grades_by_item.end() || grade_it->second <= 0) {
                    continue;
                }

                const auto rank = hit.metric_rank;
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

        /// \brief Computes a percentile using the nearest-rank method.
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

        /// \brief Summarizes measured query latencies in milliseconds.
        [[nodiscard]] LatencyStats compute_latency_stats(std::vector<double> values) {
            LatencyStats stats;
            stats.sample_count = values.size();
            if(values.empty()) {
                return stats;
            }

            std::sort(values.begin(), values.end());
            stats.mean = std::accumulate(values.begin(), values.end(), 0.0) /
                static_cast<double>(values.size());
            stats.min = values.front();
            stats.max = values.back();
            stats.p50 = percentile_nearest_rank(values, 0.50);
            stats.p95 = percentile_nearest_rank(values, 0.95);
            stats.p99 = percentile_nearest_rank(values, 0.99);
            return stats;
        }

    } // namespace

    /// \brief Checks whether a corpus item has no externally useful payload.
    bool EvalCorpusItem::empty() const noexcept {
        return id.empty() && title.empty() && text.empty();
    }

    /// \brief Checks whether an evaluation query has neither id nor text.
    bool EvalQuery::empty() const noexcept {
        return id.empty() && text.empty();
    }

    /// \brief Checks whether a relevance judgment marks an item as relevant.
    bool RelevanceJudgment::relevant() const noexcept {
        return relevance_grade > 0;
    }

    /// \brief Checks whether a dataset has no queries to evaluate.
    bool RetrievalEvalDataset::empty() const noexcept {
        return queries.empty();
    }

    /// \brief Returns the number of evaluation queries in the dataset.
    std::size_t RetrievalEvalDataset::query_count() const noexcept {
        return queries.size();
    }

    /// \brief Checks whether a query run emitted no hits.
    bool RetrievalQueryRun::empty() const noexcept {
        return hits.empty();
    }

    /// \brief Checks whether a retrieval run contains no per-query runs.
    bool RetrievalRun::empty() const noexcept {
        return queries.empty();
    }

    /// \brief Finds a metric value by cutoff.
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

    /// \brief Performs contract validation on a retrieval evaluation dataset.
    ///
    /// Surfaces the first violation found as a `std::runtime_error` whose
    /// message names the offending field. Errors are intentionally pinned to
    /// `std::runtime_error` (not `invalid_argument`) so the same exception
    /// type surfaces from both the JSON loader and in-memory constructors.
    void validate_retrieval_eval_dataset(const RetrievalEvalDataset& dataset) {
        // Corpus: non-empty ids and no duplicates within the corpus.
        {
            std::set<std::string> seen;
            for(std::size_t i = 0; i < dataset.corpus.size(); ++i) {
                if(dataset.corpus[i].id.empty()) {
                    throw std::runtime_error(
                        "validate_retrieval_eval_dataset: corpus[" +
                        std::to_string(i) + "].id must not be empty"
                    );
                }
                if(!seen.insert(dataset.corpus[i].id).second) {
                    throw std::runtime_error(
                        "validate_retrieval_eval_dataset: duplicate corpus id '"
                        + dataset.corpus[i].id + "'"
                    );
                }
            }
        }

        // Queries: non-empty ids and no duplicates. Also enforce non-empty
        // text and a strictly positive limit, and record per-query state
        // for the qrel/query-mode checks below.
        std::map<std::string, const EvalQuery*> queries_by_id;
        for(std::size_t i = 0; i < dataset.queries.size(); ++i) {
            const auto& query = dataset.queries[i];
            if(query.id.empty()) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: queries[" +
                    std::to_string(i) + "].id must not be empty"
                );
            }
            if(query.text.empty()) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: queries[" +
                    std::to_string(i) + "] (id '" + query.id +
                    "').text must not be empty"
                );
            }
            if(query.limit == 0) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: queries[" +
                    std::to_string(i) + "] (id '" + query.id +
                    "').limit must be greater than zero"
                );
            }
            if(!queries_by_id.emplace(query.id, &query).second) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: duplicate query id '"
                    + query.id + "'"
                );
            }
        }

        // Judgments: non-empty ids, no duplicates, references must resolve,
        // answer-mode consistency.
        std::set<std::pair<std::string, std::string>> seen_judgments;
        for(std::size_t i = 0; i < dataset.judgments.size(); ++i) {
            const auto& j = dataset.judgments[i];
            if(j.query_id.empty() || j.item_id.empty()) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: judgments[" +
                    std::to_string(i) + "] must have non-empty query_id/item_id"
                );
            }
            if(queries_by_id.find(j.query_id) == queries_by_id.end()) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: judgments[" +
                    std::to_string(i) + "] references unknown query id '" +
                    j.query_id + "'"
                );
            }
            // Item ids must resolve to a corpus item.
            bool item_seen = false;
            for(const auto& item : dataset.corpus) {
                if(item.id == j.item_id) {
                    item_seen = true;
                    break;
                }
            }
            if(!item_seen) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: judgments[" +
                    std::to_string(i) + "] references unknown corpus item id '"
                    + j.item_id + "'"
                );
            }
            // Fail-fast: surface negative relevance grades here so downstream
            // metric helpers do not need a second defensive check.
            if(j.relevance_grade < 0) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: judgments[" +
                    std::to_string(i) +
                    "].relevance_grade must not be negative"
                );
            }
            if(!seen_judgments.emplace(j.query_id, j.item_id).second) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: duplicate judgment at "
                    "index " + std::to_string(i) + " for query_id=" +
                    j.query_id + " item_id=" + j.item_id
                );
            }

            // NoAnswer queries must not carry positive judgments.
            const auto q_it = queries_by_id.find(j.query_id);
            if(
                j.relevance_grade > 0 &&
                q_it != queries_by_id.end() &&
                q_it->second->answer_mode == EvalQueryAnswerMode::NoAnswer
            ) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: NoAnswer query '" +
                    j.query_id + "' has positive relevance judgment for "
                    "item_id '" + j.item_id + "'"
                );
            }
        }

        // JudgedRetrieval queries must have at least one positive relevance
        // judgment; zero-grade-only entries do not satisfy the metric
        // contract because Recall@K, nDCG@K, and MRR require positive
        // grades to compute. NoAnswer queries may carry zero-grade
        // relevance judgments (explicit non-relevant markings) but must
        // not carry any positive judgments; the per-judgment loop above
        // rejects positive grades against NoAnswer queries as soon as
        // they are seen, so by this point a remaining judgment count for
        // a NoAnswer query is exclusively zero-grade entries and is
        // allowed.
        std::map<std::string, std::size_t> positive_grades_per_query;
        for(const auto& j : dataset.judgments) {
            if(j.relevance_grade > 0) {
                ++positive_grades_per_query[j.query_id];
            }
        }
        for(const auto& query : dataset.queries) {
            const auto count_it = positive_grades_per_query.find(query.id);
            const std::size_t positive_count =
                count_it == positive_grades_per_query.end() ? 0
                                                            : count_it->second;
            if(
                query.answer_mode == EvalQueryAnswerMode::JudgedRetrieval &&
                positive_count == 0
            ) {
                throw std::runtime_error(
                    "validate_retrieval_eval_dataset: JudgedRetrieval query '"
                    + query.id + "' must have at least one positive "
                    "relevance judgment"
                );
            }
        }
    }

    /// \brief Computes aggregate retrieval metrics for a dataset/run pair.
    RetrievalMetrics evaluate_retrieval(
        const RetrievalEvalDataset& dataset,
        const RetrievalRun& run,
        const RetrievalEvaluationOptions& options
    ) {
        validate_cutoffs(options);

        RetrievalMetrics metrics;
        metrics.query_count = dataset.queries.size();

        for(const auto cutoff : options.recall_cutoffs) {
            metrics.recall_at.push_back(MetricAtK{cutoff, 0.0});
        }
        for(const auto cutoff : options.ndcg_cutoffs) {
            metrics.ndcg_at.push_back(MetricAtK{cutoff, 0.0});
        }

        const auto query_modes = build_query_modes(dataset.queries);
        const auto judgments_by_query = build_judgment_map(
            dataset.judgments,
            query_modes
        );
        const auto runs_by_query = build_run_map(run.queries, query_modes);
        const auto hits_by_query = build_hit_map(runs_by_query);

        std::vector<double> latency_values;
        std::size_t no_answer_correct = 0;

        for(const auto& query : dataset.queries) {
            if(query.answer_mode == EvalQueryAnswerMode::Ignore) {
                ++metrics.ignored_query_count;
                continue;
            }

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

            const std::vector<RankedHit> empty_hits;
            const auto hits_it = hits_by_query.find(query.id);
            const auto& hits = hits_it != hits_by_query.end() ? hits_it->second : empty_hits;

            if(query.answer_mode == EvalQueryAnswerMode::NoAnswer) {
                if(query_judgments != nullptr && !query_judgments->positive_grades.empty()) {
                    throw std::invalid_argument(
                        "no-answer query must not have positive relevance judgments"
                    );
                }
                ++metrics.no_answer_query_count;
                if(hits.empty()) {
                    ++no_answer_correct;
                }
                continue;
            }

            if(query_judgments == nullptr || query_judgments->positive_grades.empty()) {
                throw std::invalid_argument(
                    "judged retrieval query must have positive relevance judgments"
                );
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
