#pragma once
#ifndef AGENT_MEMORY_HEADER_EVAL_EVALUATION_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EVAL_EVALUATION_HPP_INCLUDED

/// \file Evaluation.hpp
/// \brief Dependency-free retrieval evaluation value types and metrics.

#include <agent_memory/domain.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agent_memory {

    /// \brief Evaluation treatment for a query.
    enum class EvalQueryAnswerMode {
        /// \brief Query must have at least one positive relevance judgment.
        JudgedRetrieval,
        /// \brief Query is expected to have no answer in the corpus.
        NoAnswer,
        /// \brief Query is present in the dataset but excluded from metrics.
        Ignore
    };

    /// \brief Corpus item used by benchmark loaders and runners.
    struct EvalCorpusItem final {
        std::string id;
        std::string title;
        std::string text;
        Metadata metadata;

        [[nodiscard]] bool empty() const noexcept;
    };

    /// \brief Query in a retrieval evaluation dataset.
    struct EvalQuery final {
        std::string id;
        std::string text;
        std::string query_type;
        /// \brief Runner hint for the requested retrieval depth.
        /// \note evaluate_retrieval() does not clamp metrics by this value;
        ///       metric cutoffs come from RetrievalEvaluationOptions.
        std::size_t limit = 10;
        std::vector<MetadataFilter> metadata_filters;
        EvalQueryAnswerMode answer_mode = EvalQueryAnswerMode::JudgedRetrieval;

        [[nodiscard]] bool empty() const noexcept;
    };

    /// \brief Graded relevance judgment for one query-item pair.
    ///
    /// A relevance_grade greater than zero means relevant. Zero means judged
    /// non-relevant. Negative grades are invalid and ignored by metric helpers.
    struct RelevanceJudgment final {
        std::string query_id;
        std::string item_id;
        std::int32_t relevance_grade = 0;

        [[nodiscard]] bool relevant() const noexcept;
    };

    /// \brief BEIR-style retrieval dataset shape.
    struct RetrievalEvalDataset final {
        std::string name;
        std::vector<EvalCorpusItem> corpus;
        std::vector<EvalQuery> queries;
        std::vector<RelevanceJudgment> judgments;

        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] std::size_t query_count() const noexcept;
    };

    /// \brief One hit emitted by a retriever for a query.
    struct RetrievalRunHit final {
        std::string item_id;
        /// \brief Comparable retrieval score where higher is better.
        /// \note evaluate_retrieval() treats score as diagnostic payload; the
        ///       metric order is the hit vector order unless explicit ranks
        ///       are provided for every hit in the query run.
        float score = 0.0F;
        /// \brief Optional explicit one-based rank. Zero means "use vector position".
        /// \note Explicit ranks preserve gaps. A hit with rank 100 does not
        ///       contribute to Recall@10.
        std::size_t rank = 0;
        /// \brief Retriever/channel label such as "bm25", "dense", or "hybrid".
        std::string retriever_name;
    };

    /// \brief Hits and timing for one evaluated query.
    struct RetrievalQueryRun final {
        std::string query_id;
        std::vector<RetrievalRunHit> hits;
        /// \brief End-to-end query latency in milliseconds, if measured.
        std::optional<double> latency_ms;

        [[nodiscard]] bool empty() const noexcept;
    };

    /// \brief Full run produced by one retrieval configuration.
    struct RetrievalRun final {
        std::string name;
        std::vector<RetrievalQueryRun> queries;

        [[nodiscard]] bool empty() const noexcept;
    };

    /// \brief Metric value associated with a cutoff.
    struct MetricAtK final {
        std::size_t k = 0;
        double value = 0.0;
    };

    /// \brief Latency summary in milliseconds.
    ///
    /// `min`/`max` are populated alongside the nearest-rank percentiles so
    /// downstream reports can render the full latency envelope from a single
    /// computation path.
    struct LatencyStats final {
        std::size_t sample_count = 0;
        double mean = 0.0;
        double min = 0.0;
        double max = 0.0;
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
    };

    /// \brief Retrieval metric summary for one run.
    struct RetrievalMetrics final {
        std::size_t query_count = 0;
        std::size_t judged_query_count = 0;
        std::size_t no_answer_query_count = 0;
        std::size_t ignored_query_count = 0;

        std::vector<MetricAtK> recall_at;
        std::vector<MetricAtK> ndcg_at;
        /// \brief Unbounded mean reciprocal rank over judged queries.
        /// \note MRR@K is intentionally left for benchmark-runner follow-up PRs.
        double mrr = 0.0;
        double no_answer_accuracy = 0.0;

        LatencyStats latency_ms;
    };

    /// \brief Options for evaluating a retrieval run.
    struct RetrievalEvaluationOptions final {
        std::vector<std::size_t> recall_cutoffs{1, 5, 10, 50};
        std::vector<std::size_t> ndcg_cutoffs{10};
    };

    /// \brief Returns the first metric value for the requested cutoff.
    [[nodiscard]] std::optional<double> metric_value_at(
        const std::vector<MetricAtK>& metrics,
        std::size_t k
    ) noexcept;

    /// \brief Computes retrieval metrics for a run against a dataset.
    ///
    /// JudgedRetrieval queries must have at least one positive relevance
    /// judgment and participate in Recall@K, unbounded MRR, and nDCG@K.
    /// NoAnswer queries participate only in no_answer_accuracy. Ignore queries
    /// are counted and skipped. Missing query runs are evaluated as empty hit
    /// lists.
    /// \note This metric-only helper validates queries, judgments, and runs,
    ///       but does not require judged item ids to be present in
    ///       RetrievalEvalDataset::corpus. Corpus/qrels integrity belongs to
    ///       dataset loader or benchmark runner validation.
    /// \throws std::invalid_argument when query ids, qrels, runs, ranks,
    ///         scores, latency samples, or metric cutoffs violate the
    ///         evaluation contract.
    [[nodiscard]] RetrievalMetrics evaluate_retrieval(
        const RetrievalEvalDataset& dataset,
        const RetrievalRun& run,
        const RetrievalEvaluationOptions& options = {}
    );

} // namespace agent_memory

#endif
