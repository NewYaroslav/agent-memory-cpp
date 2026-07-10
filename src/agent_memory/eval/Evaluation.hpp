#pragma once
#ifndef AGENT_MEMORY_HEADER_EVAL_EVALUATION_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_EVAL_EVALUATION_HPP_INCLUDED

/// \file Evaluation.hpp
/// \brief Dependency-free retrieval evaluation value types and metrics.

#include <agent_memory/domain/Domain.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace agent_memory {

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
        std::size_t limit = 10;
        std::vector<MetadataFilter> metadata_filters;

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
        float score = 0.0F;
        /// \brief Optional explicit rank. Zero means "use vector position".
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
    struct LatencyStats final {
        std::size_t sample_count = 0;
        double mean = 0.0;
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
    };

    /// \brief Retrieval metric summary for one run.
    struct RetrievalMetrics final {
        std::size_t query_count = 0;
        std::size_t judged_query_count = 0;
        std::size_t no_answer_query_count = 0;

        std::vector<MetricAtK> recall_at;
        std::vector<MetricAtK> ndcg_at;
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
    /// Queries with at least one positive relevance judgment participate in
    /// Recall@K, MRR, and nDCG@K. Queries without positive judgments are treated
    /// as no-answer cases and only participate in no_answer_accuracy.
    /// Missing query runs are evaluated as empty hit lists.
    [[nodiscard]] RetrievalMetrics evaluate_retrieval(
        const RetrievalEvalDataset& dataset,
        const RetrievalRun& run,
        const RetrievalEvaluationOptions& options = {}
    );

} // namespace agent_memory

#endif
